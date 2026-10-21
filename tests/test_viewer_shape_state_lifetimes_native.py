"""Finite S4/S5/S6 source lifetime contracts, reusing the unchanged Shape oracle.

No application execution belongs to this author. Callback witnesses use ordinary
finalizers/weakrefs and public sys.exception(); no private frame/refcount checks.
"""

# Each callback/witness is used before the next source-family iteration.
# ruff: noqa: B023
from __future__ import annotations

import hashlib
import importlib.util
import inspect
import sys
import weakref
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / ".artifacts/test_viewer_shape_state_next.py"
assert hashlib.sha256(FIXTURE.read_bytes()).hexdigest() == (
    "efa3f8b802a2e7956659c874ba29305e72937517fc6a84bd5d0ab08193bbe36a"
)
SPEC = importlib.util.spec_from_file_location(
    "_naparixx_shape_state_lifetimes_fixture", FIXTURE
)
assert SPEC is not None and SPEC.loader is not None
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)
families = ORACLE.families


def _body(family, name, setter=False):
    return inspect.unwrap(ORACLE._function(family.ShapeList, name, setter))


@pytest.mark.parametrize(
    "operation", ["init", "colors", "slice_key", "multi", "single"]
)
def test_reassigned_parameter_drops_before_later_callback_and_saved_error(
    families, monkeypatch, operation
):
    for family in families:
        trace, references = [], {}
        error = RuntimeError("after parameter replacement")
        shape = ORACLE._rectangle()

        class Parameter:
            def __init__(self, label):
                self.label = label
                references[label] = weakref.ref(self)

            def __iter__(self):
                trace.append(("iterate", self.label))
                return iter([shape] if self.label == "data" else [7])

            def __del__(self):
                trace.append(("drop", self.label))

        def later(*args, **kwargs):
            trace.append("later")
            assert all(reference() is None for reference in references.values())
            raise error

        with monkeypatch.context() as patch:
            if operation == "init":
                owner = object.__new__(family.ShapeList)
                owner.add = later
                with pytest.raises(RuntimeError) as caught:
                    family.ShapeList.__init__(owner, Parameter("data"))
                expected = [("iterate", "data"), ("drop", "data"), "later"]
            elif operation == "colors":

                def cast(value, *args, **kwargs):
                    trace.append(("cast", value.label))
                    return ORACLE.np.ones((1, 4), dtype=ORACLE.np.float32)

                class Shapes:
                    def __len__(self):
                        return later()

                patch.setitem(
                    family.shape_namespace, "np", ORACLE.NumpyProxy(asarray=cast)
                )
                with pytest.raises(RuntimeError) as caught:
                    family.shape_module._ensure_color_arrays(
                        Shapes(), Parameter("face"), Parameter("edge")
                    )
                expected = [
                    ("cast", "face"),
                    ("drop", "face"),
                    ("cast", "edge"),
                    ("drop", "edge"),
                    "later",
                ]
            elif operation == "slice_key":
                owner = family.ShapeList()
                patch.setitem(
                    family.shape_namespace, "np", ORACLE.NumpyProxy(array_equal=later)
                )
                # Exercise the complete setter body. The metadata decorator's
                # argument tuple would itself be a genuine additional source root.
                with pytest.raises(RuntimeError) as caught:
                    _body(family, "slice_key", True)(owner, Parameter("slice"))
                expected = [("iterate", "slice"), ("drop", "slice"), "later"]
            elif operation == "multi":
                owner = family.ShapeList()

                def ensure(shapes, face, edge):
                    trace.append("ensure")
                    return ORACLE.np.ones((1, 4)), ORACLE.np.zeros((1, 4))

                patch.setitem(family.shape_namespace, "_ensure_color_arrays", ensure)
                patch.setitem(family.shape_namespace, "_calculate_array_sizes", later)
                with pytest.raises(RuntimeError) as caught:
                    owner._add_multiple_shapes(
                        [shape], Parameter("face"), Parameter("edge")
                    )
                expected = ["ensure", ("drop", "face"), ("drop", "edge"), "later"]
            else:
                owner = family.ShapeList([shape])

                class Items(list):
                    def __setitem__(self, index, value):
                        later(index, value)

                owner.shapes = Items(owner.shapes)
                with pytest.raises(RuntimeError) as caught:
                    owner._add_single_shape(
                        ORACLE._rectangle(10),
                        shape_index=0,
                        z_refresh=Parameter("refresh"),
                    )
                expected = [("drop", "refresh"), "later"]
        assert caught.value is error and error.__traceback__ is not None
        assert trace == expected
        # The later exception is deliberately still retained: its original source
        # locals contain replacement values, not the overwritten arguments.
        assert all(reference() is None for reference in references.values())


@pytest.mark.parametrize(
    "site", ["keys", "visible", "mesh_ranges", "vertex_ranges", "z_order"]
)
@pytest.mark.parametrize("failure", [False, True])
def test_inlined_comprehension_iterator_list_and_current_cleanup(
    families, monkeypatch, site, failure
):
    for family in families:
        owner = family.ShapeList()
        trace, references = [], {}
        error = RuntimeError("second comprehension body")
        result = object()
        calls = 0

        class Value:
            def __init__(self, label):
                self.label = label
                references[f"value:{label}"] = weakref.ref(self)

            def __del__(self):
                trace.append(("value-drop", self.label))

        def evaluate(label):
            trace.append(("body", label))
            if failure and label == "last":
                raise error
            return Value(label)

        class Item:
            def __init__(self, label):
                self.label = label
                references[f"item:{label}"] = weakref.ref(self)

            def __index__(self):
                return 0 if self.label == "first" else 1

            @property
            def slice_key(self):
                label = self.label
                del self  # Callback traceback must not add an item root.
                return evaluate(label)

            def __getitem__(self, key):
                assert key == 0
                label = self.label
                del self
                return evaluate(label)

            def __del__(self):
                trace.append(
                    (
                        "item-drop",
                        self.label,
                        references["iterator"]() is not None,
                        references.get("value:first", lambda: None)() is not None,
                    )
                )

        class Iterator:
            def __init__(self):
                self.position = 0
                references["iterator"] = weakref.ref(self)
                trace.append("iterator-create")

            def __iter__(self):
                return self

            def __next__(self):
                if self.position == 2:
                    raise StopIteration
                label = "first" if self.position == 0 else "last"
                self.position += 1
                return Item(label)  # No external/current item root on the iterator.

            def __del__(self):
                trace.append("iterator-drop")

        class Sequence:
            def __len__(self):
                return 2

            def __getitem__(self, index):
                # Only the pre-comprehension discontinuity check reads these.
                return 3 if index == -1 else 0

            def __iter__(self):
                return Iterator()

        def slice_for(item):
            label = item.label
            del item
            return evaluate(label)

        def arange(*args, **kwargs):
            nonlocal calls
            label = "first" if calls == 0 else "last"
            calls += 1
            return evaluate(label)

        def consume(values, *args, **kwargs):
            trace.append("consume")
            assert isinstance(values, list)
            assert references["iterator"]() is None
            assert references["item:last"]() is None
            assert values[0] is references["value:first"]()
            assert values[1] is references["value:last"]()
            return result

        with monkeypatch.context() as patch:
            if site == "keys":
                owner.shapes = Sequence()
                patch.setitem(
                    family.shape_namespace, "np", ORACLE.NumpyProxy(array=consume)
                )

                def call():
                    return _body(family, "slice_keys")(owner)
            elif site == "visible":
                vars(owner)["_visible_shapes"] = Sequence()
                patch.setitem(
                    family.shape_namespace, "np", ORACLE.NumpyProxy(array=consume)
                )

                def call():
                    return _body(family, "_visible_shapes_indices")(owner)
            elif site in {"mesh_ranges", "vertex_ranges"}:
                name = (
                    "_mesh_triangles_slice"
                    if site == "mesh_ranges"
                    else "_vertices_slice"
                )
                setattr(owner, name, slice_for)
                owner._slicess_to_array = consume
                method = (
                    "_mesh_triangles_range_seq"
                    if site == "mesh_ranges"
                    else "_vertices_range_seq"
                )

                def call():
                    return getattr(owner, method)(Sequence())
            else:
                owner._mesh.triangles_index = ORACLE.np.array([0, 1, 2])
                owner._mesh.triangles = ORACLE.np.empty((3, 3), dtype=int)
                owner._update_displayed = lambda: trace.append("displayed")
                patch.setitem(
                    family.shape_namespace,
                    "np",
                    ORACLE.NumpyProxy(
                        argsort=lambda *a, **k: Sequence(),
                        arange=arange,
                        concatenate=consume,
                    ),
                )

                def call():
                    return _body(family, "_update_z_order")(owner)

            if failure:
                with pytest.raises(RuntimeError) as caught:
                    call()
                assert caught.value is error and error.__traceback__ is not None
                trace.append("caught")
            else:
                returned = call()
                if site == "z_order":
                    assert returned is None and owner._mesh.triangles_z_order is result
                else:
                    assert returned is result
                trace.append("after")
        prefix = [
            "iterator-create",
            ("body", "first"),
            ("item-drop", "first", True, True),
            ("body", "last"),
        ]
        if failure:
            expected = prefix + [
                "iterator-drop",
                ("value-drop", "first"),
                ("item-drop", "last", False, False),
                "caught",
            ]
        else:
            middle = ["iterator-drop", ("item-drop", "last", False, True), "consume"]
            tail = [("value-drop", "last"), ("value-drop", "first")]
            if site == "z_order":
                # The assigned triangles_z_order list remains a source local
                # until after the following _update_displayed call.
                tail = ["displayed", *tail]
            expected = prefix + middle + tail + ["after"]
        assert trace == expected
        assert all(reference() is None for reference in references.values())


@pytest.mark.parametrize("site", ["wrapped", "ndisplay"])
@pytest.mark.parametrize(
    "branch", ["success", "body_error", "suppressed", "enter_error"]
)
def test_consumed_context_manager_enter_result_and_saved_exit(families, site, branch):
    for family in families:
        trace, references = [], {}
        prior = RuntimeError("prior handled exception")
        error = RuntimeError("context callback")
        result = object()
        owner = family.ShapeList()

        class EnterResult:
            def __del__(self):
                trace.append("enter-result-drop")

        class Enter:
            def __init__(self):
                references["enter"] = weakref.ref(self)

            def __call__(self):
                trace.append("enter")
                assert references["manager"]() is None
                if branch == "enter_error":
                    del self  # A saved callback traceback is not an extra root.
                    raise error
                return EnterResult()

            def __del__(self):
                trace.append("enter-drop")

        class Truth:
            def __bool__(self):
                trace.append(
                    (
                        "truth",
                        references["exit"]() is not None,
                        sys.exception() is error,
                    )
                )
                return True

            def __del__(self):
                trace.append("truth-drop")

        class Exit:
            def __init__(self):
                references["exit"] = weakref.ref(self)

            def __call__(self, typ, value, tb):
                trace.append(("exit", None if typ is None else typ.__name__))
                if branch == "suppressed":
                    assert value is error and tb is not None
                    return Truth()
                return False

            def __del__(self):
                trace.append(("exit-drop", sys.exception() is prior))

        class Descriptor:
            def __init__(self, name, factory):
                self.name, self.factory = name, factory

            def __get__(self, instance, cls):
                trace.append(("capture", self.name))
                return self.factory()  # Does not retain the consumed manager.

        class Manager:
            __enter__ = Descriptor("enter", Enter)
            __exit__ = Descriptor("exit", Exit)

            def __init__(self):
                references["manager"] = weakref.ref(self)

            def __del__(self):
                trace.append("manager-drop")

        def context():
            trace.append("context")
            return Manager()

        def body(*args, **kwargs):
            trace.append("body")
            assert references["manager"]() is None and references["enter"]() is None
            if branch in {"body_error", "suppressed"}:
                raise error
            return result

        owner.batched_updates = context
        if site == "wrapped":

            def call():
                return family.shape_module._batch_dec(body)(owner)
        else:
            owner.remove_all = body
            owner._add_multiple_shapes = lambda *a, **k: trace.append("rebuild")

            def call():
                return _body(family, "ndisplay", True)(owner, 3)

        try:
            raise prior
        except RuntimeError:
            if branch in {"body_error", "enter_error"}:
                with pytest.raises(RuntimeError) as caught:
                    call()
                assert caught.value is error
            else:
                returned = call()
                expected_result = (
                    result if site == "wrapped" and branch == "success" else None
                )
                assert returned is expected_result
        expected = [
            "context",
            ("capture", "enter"),
            ("capture", "exit"),
            "manager-drop",
            "enter",
            "enter-drop",
        ]
        if branch == "enter_error":
            expected += [("exit-drop", True)]
        else:
            expected += ["enter-result-drop", "body"]
            if site == "ndisplay" and branch == "success":
                expected += ["rebuild"]
            expected += [("exit", None if branch == "success" else "RuntimeError")]
            if branch == "suppressed":
                expected += [("truth", True, True), "truth-drop"]
            expected += [("exit-drop", True)]
        assert trace == expected
        assert all(reference() is None for reference in references.values())
        if site == "ndisplay":
            assert owner.ndisplay == owner._mesh.ndisplay == 3
